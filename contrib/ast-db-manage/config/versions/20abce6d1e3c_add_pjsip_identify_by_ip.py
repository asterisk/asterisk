"""add pjsip identify by ip

Revision ID: 20abce6d1e3c
Revises: a1698e8bb9c5
Create Date: 2017-10-24 15:44:06.404774

"""

# revision identifiers, used by Alembic.
revision = '20abce6d1e3c'
down_revision = 'a1698e8bb9c5'

from alembic import op
import sqlalchemy as sa


def enum_update(table_name, column_name, enum_name, enum_values):
    if op.get_context().bind.dialect.name != 'postgresql':
        if op.get_context().bind.dialect.name == 'mssql':
            op.drop_constraint('ck_ps_endpoints_identify_by_pjsip_identify_by_values', 'ps_endpoints')
        op.alter_column(table_name, column_name,
                        type_=sa.Enum(*enum_values, name=enum_name))
        return

    # Postgres requires a few more steps
    tmp = enum_name + '_tmp'

    op.execute('ALTER TYPE ' + enum_name + ' RENAME TO ' + tmp)

    updated = sa.Enum(*enum_values, name=enum_name)
    updated.create(op.get_bind(), checkfirst=False)

    op.execute('ALTER TABLE ' + table_name + ' ALTER COLUMN ' + column_name +
               ' TYPE ' + enum_name + ' USING identify_by::text::' + enum_name)

    op.execute('DROP TYPE ' + tmp)


def upgrade():
    enum_update('ps_endpoints', 'identify_by', 'pjsip_identify_by_values',
                ['username', 'auth_username', 'ip'])


def downgrade():
    enum_update('ps_endpoints', 'identify_by', 'pjsip_identify_by_values',
                ['username', 'auth_username'])
