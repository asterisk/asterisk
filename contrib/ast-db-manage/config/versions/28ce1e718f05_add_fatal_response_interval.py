"""add fatal_response_interval

Revision ID: 28ce1e718f05
Revises: 154177371065
Create Date: 2015-10-20 17:57:45.560585

"""

# revision identifiers, used by Alembic.
revision = '28ce1e718f05'
down_revision = '154177371065'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_registrations', sa.Column('fatal_retry_interval', sa.Integer))


def downgrade():
    with op.batch_alter_table('ps_registrations') as batch_op:
        batch_op.drop_column('fatal_retry_interval')
