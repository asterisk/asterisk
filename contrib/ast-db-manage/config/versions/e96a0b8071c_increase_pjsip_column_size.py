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
    with op.batch_alter_table('ps_globals') as batch_op:
        batch_op.alter_column('user_agent', type_=sa.String(255))

    with op.batch_alter_table('ps_contacts') as batch_op:
        batch_op.alter_column('id', type_=sa.String(255))
        batch_op.alter_column('uri', type_=sa.String(255))
        batch_op.alter_column('user_agent', type_=sa.String(255))

    with op.batch_alter_table('ps_registrations') as batch_op:
        batch_op.alter_column('client_uri', type_=sa.String(255))
        batch_op.alter_column('server_uri', type_=sa.String(255))


def downgrade():
    with op.batch_alter_table('ps_globals') as batch_op:
        batch_op.alter_column('user_agent', type_=sa.String(40))

    with op.batch_alter_table('ps_contacts') as batch_op:
        batch_op.alter_column('id', type_=sa.String(40))
        batch_op.alter_column('uri', type_=sa.String(40))
        batch_op.alter_column('user_agent', type_=sa.String(40))

    with op.batch_alter_table('ps_registrations') as batch_op:
        batch_op.alter_column('client_uri', type_=sa.String(40))
        batch_op.alter_column('server_uri', type_=sa.String(40))
