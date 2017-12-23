"""add pjsip identify by header

Revision ID: 52798ad97bdf
Revises: e2f04d309071
Create Date: 2018-01-08 12:16:02.782277

"""

# revision identifiers, used by Alembic.
revision = '52798ad97bdf'
down_revision = 'e2f04d309071'

from alembic import op
import sqlalchemy as sa


def column_upgrade(table_name, column_name, enum_name):
    if op.get_context().bind.dialect.name != 'postgresql':
        if op.get_context().bind.dialect.name == 'mssql':
            op.drop_constraint('ck_ps_endpoints_identify_by_pjsip_identify_by_values',
                               table_name)
        op.alter_column(table_name, column_name, type_=sa.String(80))
        return

    # Postgres requires a few more steps
    op.execute('ALTER TABLE ' + table_name + ' ALTER COLUMN ' + column_name +
               ' TYPE varchar(80) USING identify_by::text::' + enum_name)

    op.execute('DROP TYPE ' + enum_name)


def column_downgrade(table_name, column_name, enum_name, enum_values):
    if op.get_context().bind.dialect.name != 'postgresql':
        op.alter_column(table_name, column_name,
                        type_=sa.Enum(*enum_values, name=enum_name))
        return

    # Postgres requires a few more steps
    updated = sa.Enum(*enum_values, name=enum_name)
    updated.create(op.get_bind(), checkfirst=False)

    op.execute('ALTER TABLE ' + table_name + ' ALTER COLUMN ' + column_name +
               ' TYPE ' + enum_name + ' USING identify_by::text::' + enum_name)


def upgrade():
    # The ps_endpoints identify_by column has always been a comma separated
    # list of enum values.  This is better represented as a string anyway to
    # avoid database compatibility issues.  Also future changes are likely
    # to allow loadable endpoint identifier names and negating fixed enum
    # benefits.
    column_upgrade('ps_endpoints', 'identify_by', 'pjsip_identify_by_values')


def downgrade():
    column_downgrade('ps_endpoints', 'identify_by', 'pjsip_identify_by_values',
                     ['username', 'auth_username', 'ip'])
