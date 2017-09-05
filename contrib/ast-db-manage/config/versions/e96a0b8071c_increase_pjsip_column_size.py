"""increase pjsip columns size

Revision ID: e96a0b8071c
Revises: 3855ee4e5f85
Create Date: 2014-04-23 11:38:02.333786

"""

# revision identifiers, used by Alembic.
revision = 'e96a0b8071c'
down_revision = '3855ee4e5f85'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.alter_column('ps_globals', 'user_agent', type_=sa.String(255))

    op.alter_column('ps_contacts', 'id', type_=sa.String(255))
    op.alter_column('ps_contacts', 'uri', type_=sa.String(255))
    op.alter_column('ps_contacts', 'user_agent', type_=sa.String(255))

    op.alter_column('ps_registrations', 'client_uri', type_=sa.String(255))
    op.alter_column('ps_registrations', 'server_uri', type_=sa.String(255))


def downgrade():
    op.alter_column('ps_globals', 'user_agent', type_=sa.String(40))

    if op.get_context().bind.dialect.name != 'mssql':
        op.alter_column('ps_contacts', 'id', type_=sa.String(40))
    else:
        op.drop_constraint('uq_ps_contacts_id', 'ps_contacts')
        op.drop_index('ps_contacts_id', 'ps_contacts')
        op.alter_column('ps_contacts', 'id', type_=sa.String(40))
        op.create_index('ps_contacts_id', 'ps_contacts', ['id'])
    op.alter_column('ps_contacts', 'uri', type_=sa.String(40))
    op.alter_column('ps_contacts', 'user_agent', type_=sa.String(40))

    op.alter_column('ps_registrations', 'client_uri', type_=sa.String(40))
    op.alter_column('ps_registrations', 'server_uri', type_=sa.String(40))
